import React, { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import Header from '../components/Header';
import { API_BASE } from '../api.js';
import './dashboard/DashboardPage.css'; // Reusing dashboard styles

export default function DistributorPage() {
  const navigate = useNavigate();
  const [user, setUser] = useState(null);
  const [keys, setKeys] = useState([]);

  useEffect(() => {
    const token = localStorage.getItem('token');
    if (!token) {
      navigate('/');
      return;
    }

    // Check if user is distributor
    fetch(`${API_BASE}/api/user/me`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
      .then(res => res.json())
      .then(data => {
        if (data.error) {
          navigate('/');
        } else if (!data.is_distributor) {
          navigate('/home'); // Redirect to dashboard if not distributor
        } else {
          setUser(data);
          fetchKeys(token);
        }
      })
      .catch(console.error);
  }, [navigate]);

  const fetchKeys = (token) => {
    fetch(`${API_BASE}/api/distributor/keys`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
      .then(res => res.json())
      .then(data => {
        if (data.ok) setKeys(data.keys);
      })
      .catch(console.error);
  };

  const activeKeys = keys.filter(k => !k.used);
  const usedKeys = keys.filter(k => k.used);

  if (!user) return <div className="page-loading">Загрузка…</div>;

  return (
    <div className="dashboard-page">
      <Header />

      <div className="container dashboard-container">
        <h2 className="dash-page-title">Кабинет Дистрибьютора</h2>

        <div className="dash-row dash-row-3">
          <div className="dash-card info-card" style={{ flex: 1 }}>
            <div className="info-label">Всего выдано ключей</div>
            <div className="info-val">{keys.length}</div>
          </div>
          <div className="dash-card info-card" style={{ flex: 1 }}>
            <div className="info-label">Неиспользованных</div>
            <div className="info-val is-success">{activeKeys.length}</div>
          </div>
          <div className="dash-card info-card" style={{ flex: 1 }}>
            <div className="info-label">Активировано</div>
            <div className="info-val is-danger">{usedKeys.length}</div>
          </div>
        </div>

        <div className="dash-card keys-card">
          <h3 className="action-title">Ваши ключи</h3>

          <div className="keys-table-wrap">
            <table className="keys-table">
              <thead>
                <tr>
                  <th>Ключ</th>
                  <th>Продукт</th>
                  <th>Дней</th>
                  <th>Статус</th>
                  <th>Дата выдачи</th>
                </tr>
              </thead>
              <tbody>
                {keys.length > 0 ? (
                  keys.map((k, i) => (
                    <tr key={i}>
                      <td className="tnum">
                        {k.key_str}
                        <button
                          type="button"
                          className="copy-key"
                          onClick={() => { navigator.clipboard.writeText(k.key_str); }}
                        >
                          Копировать
                        </button>
                      </td>
                      <td className="product-name">{k.product_id}</td>
                      <td>{k.days} дн.</td>
                      <td>
                        {k.used ? (
                          <span className="sub-status is-danger">Активирован</span>
                        ) : (
                          <span className="sub-status is-active">Свободен</span>
                        )}
                      </td>
                      <td className="keys-date">
                        {new Date(k.created_at).toLocaleString('ru-RU')}
                      </td>
                    </tr>
                  ))
                ) : (
                  <tr>
                    <td colSpan="5" className="keys-empty">У вас пока нет ключей. Обратитесь к администратору.</td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </div>

      </div>
    </div>
  );
}
