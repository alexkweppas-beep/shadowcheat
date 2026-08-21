import React, { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import Header from '../components/Header';
import { API_BASE } from '../api.js';
import './dashboard/DashboardPage.css'; // Reusing dashboard styles

export default function MediaPage() {
  const navigate = useNavigate();
  const [user, setUser] = useState(null);
  const [stats, setStats] = useState(null);

  useEffect(() => {
    const token = localStorage.getItem('token');
    if (!token) {
      navigate('/');
      return;
    }
    
    // Check if user is media
    fetch(`${API_BASE}/api/user/me`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    .then(res => res.json())
    .then(data => {
      if (data.error) {
        navigate('/');
      } else if (!data.media_access) {
        navigate('/home'); // Redirect to dashboard if not media
      } else {
        setUser(data);
        fetchStats(token);
      }
    })
    .catch(console.error);
  }, [navigate]);

  const fetchStats = (token) => {
    fetch(`${API_BASE}/api/media/stats`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    .then(res => res.json())
    .then(data => {
      if (data.ok) setStats(data);
    })
    .catch(console.error);
  };

  if (!user) return <div style={{color:'white', padding:'40px', textAlign:'center'}}>Загрузка...</div>;
  const currentStats = stats || { promocode: user.promocode || '', promo_percent: user.promo_percent || 0, total_uses: 0, promo_earned: user.promo_earned || 0 };

  return (
    <div className="dashboard-page">
      <Header />
      
      <div className="container dashboard-container">
        <h2 style={{color: 'white', marginBottom: '20px', fontSize: '24px'}}>Кабинет Медиа-партнера</h2>
        
        <div className="dash-row dash-row-3">
          <div className="dash-card info-card">
            <div className="info-label">Ваш промокод</div>
            <div className="info-val" style={{color: '#4ade80'}}>{currentStats.promocode || 'НЕ УСТАНОВЛЕН'}</div>
          </div>
          <div className="dash-card info-card">
            <div className="info-label">Процент с продаж</div>
            <div className="info-val">{currentStats.promo_percent}%</div>
          </div>
          <div className="dash-card info-card">
            <div className="info-label">Активаций промокода</div>
            <div className="info-val">{currentStats.total_uses}</div>
          </div>
        </div>

        <div className="dash-row dash-row-2">
          <div className="dash-card balance-card">
            <div className="balance-header">
              <div className="balance-info">
                <span className="balance-label">Заработано по промокоду</span>
                <span className="balance-value" style={{color: '#4ade80'}}>{currentStats.promo_earned} рублей</span>
              </div>
            </div>
            <p style={{color: 'rgba(255,255,255,0.5)', marginTop: '20px', fontSize: '14px'}}>
              Средства начисляются автоматически при покупках по вашему промокоду. Для вывода средств обратитесь к администрации.
            </p>
          </div>
        </div>

      </div>
    </div>
  );
}
