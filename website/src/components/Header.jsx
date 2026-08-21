import React, { useState, useRef, useEffect } from 'react';
import { NavLink, useNavigate } from 'react-router-dom';
import './Header.css';
import AuthModal from './AuthModal';

export default function Header() {
  const [isProductsOpen, setIsProductsOpen] = useState(false);
  const [isAuthOpen, setIsAuthOpen] = useState(false);
  const dropdownRef = useRef(null);
  const navigate = useNavigate();

  // Close dropdown on outside click
  useEffect(() => {
    function handleClickOutside(event) {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target)) {
        setIsProductsOpen(false);
      }
    }
    document.addEventListener("mousedown", handleClickOutside);
    return () => document.removeEventListener("mousedown", handleClickOutside);
  }, [dropdownRef]);

  const products = [
    {
      id: 'external',
      title: 'EXTERNAL',
      subtitle: '',
      status: 'Undetected',
      color: 'transparent',
      icon: <img src="/external.png" style={{ width: '24px', height: '24px', objectFit: 'contain', transform: 'scale(1.7)' }} alt="EXTERNAL" />
    },
    {
      id: 'trigger',
      title: 'TRIGGER',
      subtitle: '',
      status: 'Undetected',
      color: 'transparent',
      icon: <img src="/external.png" style={{ width: '24px', height: '24px', objectFit: 'contain', transform: 'scale(1.7)' }} alt="TRIGGER" />
    }
  ];

  const handleProductClick = (gameId) => {
    setIsProductsOpen(false);
    navigate(`/games/${gameId}`);
  };

  const handleLogin = () => {
    setIsAuthOpen(false);
    navigate('/home');
  };

  const handleDashboardClick = () => {
    const token = localStorage.getItem('token');
    if (token) {
      navigate('/home');
    } else {
      setIsAuthOpen(true);
    }
  };

  const handleLogout = () => {
    localStorage.removeItem('token');
    localStorage.removeItem('adminToken');
    navigate('/');
    window.location.reload();
  };

  const token = localStorage.getItem('token');

  return (
    <header className="main-header">
      <div className="container header-container">
        <div className="logo" onClick={() => navigate('/')}>
          <img src="/logo.svg" className="logo-icon" alt="Logo" />
        </div>

        <nav className="main-nav">
          <div className="dropdown-container">
            <button 
              className={`nav-link dropdown-toggle ${isProductsOpen ? 'active' : ''}`}
              onClick={() => setIsProductsOpen(!isProductsOpen)}
            >
              Товары
              <svg className="dropdown-arrow" style={{marginLeft: '4px'}} width="10" height="6" viewBox="0 0 10 6" fill="none" xmlns="http://www.w3.org/2000/svg">
                <path d="M1 1L5 5L9 1" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/>
              </svg>
            </button>

            {/* Dropdown Menu */}
            {isProductsOpen && (
              <div className="products-dropdown" ref={dropdownRef}>
                <div className="products-grid">
                  {products.map((p) => (
                    <div 
                      key={p.id} 
                      className="product-dropdown-card"
                      onClick={() => handleProductClick(p.id)}
                    >
                      <div className="product-icon">
                        {p.icon}
                      </div>
                      <div className="product-name">
                        {p.title}<br/><span style={{fontWeight:400,fontSize:12,color:'var(--text-secondary)'}}>{p.subtitle}</span>
                      </div>
                      <div className="product-status">{p.status}</div>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </div>
          <a href="#" className="nav-link" style={{textDecoration: 'none'}}>Новости</a>
          <a href="#" className="nav-link" style={{textDecoration: 'none'}}>FAQ</a>
        </nav>

        <div className="header-actions" style={{ display: 'flex', gap: '12px', alignItems: 'center' }}>
          <button className="btn-secondary" onClick={handleDashboardClick}>
            Личный кабинет
          </button>
          {token && (
            <button className="btn-secondary" onClick={handleLogout}>
              Выйти
            </button>
          )}
        </div>
      </div>

      {/* Modals */}
      <AuthModal 
        isOpen={isAuthOpen} 
        onClose={() => setIsAuthOpen(false)} 
        onLogin={handleLogin}
      />
    </header>
  );
}
